import React from 'react';
import Codebase from '../../components/codebase';

import { graphql } from 'gatsby';
import { Match } from '@reach/router';

export const query = graphql`
  query {
    allDts {
      nodes {
        filename
        internal {
          content
        }
      }
    }
  }
`;

function Repo({ data }) {
  const dts = Object.fromEntries(
    data.allDts.nodes.map(
      ({ filename, internal }) => [filename, internal.content]
    )
  );
  return (
    <Match path="/repo/*">
      {props => props.match && (
        <Codebase
          root={'/' + props.match['*']}
          dts={dts}
        />
      )}
    </Match>
  );
}

export default Repo;